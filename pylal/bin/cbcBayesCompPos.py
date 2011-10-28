#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#       cbcBayesCompPos.py
#       Copyright 2010 Benjamin Aylott <benjamin.aylott@ligo.org>
#
#       This program is free software; you can redistribute it and/or modify
#       it under the terms of the GNU General Public License as published by
#       the Free Software Foundation; either version 2 of the License, or
#       (at your option) any later version.
#
#       This program is distributed in the hope that it will be useful,
#       but WITHOUT ANY WARRANTY; without even the implied warranty of
#       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#       GNU General Public License for more details.
#
#       You should have received a copy of the GNU General Public License
#       along with this program; if not, write to the Free Software
#       Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
#       MA 02110-1301, USA.

#standard library imports
import os
import sys
from time import strftime
import copy
import random

#related third party imports
import numpy as np
import matplotlib as mpl
mpl.use("AGG")
from matplotlib import pyplot as plt
from matplotlib import colors as mpl_colors


from scipy import stats

#local application/library specific imports
import pylal.bayespputils as bppu
from pylal import SimInspiralUtils
from pylal import git_version

__author__="Ben Aylott <benjamin.aylott@ligo.org>"
__version__= "git id %s"%git_version.id
__date__= git_version.date

#List of parameters to plot/bin . Need to match (converted) column names.
oneDMenu=['mtotal','m1','m2','mchirp','mc','distance','distMPC','dist','iota','psi','eta','a1','a2','phi1','theta1','phi2','theta2']
#List of parameter pairs to bin . Need to match (converted) column names.
twoDGreedyMenu=[['mc','eta'],['mchirp','eta'],['m1','m2'],['mtotal','eta'],['distance','iota'],['dist','iota'],['dist','m1'],['ra','dec'],['dist','cos(iota)']]
#Bin size/resolution for binning. Need to match (converted) column names.

greedyBinSizes={'mc':0.0001,'m1':0.1,'m2':0.1,'mass1':0.1,'mass2':0.1,'mtotal':0.1,'eta':0.001,'iota':0.01,'time':1e-4,'distance':1.0,'dist':1.0,'mchirp':0.001,'a1':0.02,'a2':0.02,'phi1':0.05,'phi2':0.05,'theta1':0.05,'theta2':0.05,'ra':0.05,'dec':0.005,'psi':0.1,'cos(iota)':0.01}

#Confidence levels
confidenceLevels=[0.67,0.9,0.95]
#2D plots list
#twoDplots=[['mc','eta'],['mchirp','eta'],['m1','m2'],['mtotal','eta'],['distance','iota'],['dist','iota'],['RA','dec'],['ra','dec'],['m1','dist'],['m2','dist'],['psi','iota'],['psi','distance'],['psi','dist'],['psi','phi0'],['dist','cos(iota)']]
twoDplots=[['m1','m2'],['mass1','mass2'],['RA','dec'],['ra','dec']]
allowed_params=['mtotal','m1','m2','mchirp','mc','distance','distMPC','dist','iota','psi','eta','ra','dec','a1','a2','phi1','theta1','phi2','theta2','cos(iota)']

def open_url(url,username,password):

    import urllib
    import urllib2
    import urlparse

    parsed_url=urlparse.urlparse(url)
    url=urlparse.urljoin(parsed_url.geturl(),'posterior_samples.dat')


    opener = urllib2.build_opener(urllib2.HTTPCookieProcessor())
    urllib2.install_opener(opener)

    body={'username':username,'password':password}
    txdata = urllib.urlencode(body) # if we were making a POST type request, we could encode a dictionary of values here - using urllib.urlencode
    txheaders = {'User-agent' : 'Mozilla/4.0 (compatible; MSIE 5.5; Windows NT)'} # fake a user agent, some websites (like google) don't like automated exploration

    req = urllib2.Request(parsed_url[0]+'://'+parsed_url[1], txdata, txheaders)

    resp = opener.open(req) # save a cookie
    dump=resp.read()
    resp.close()
    try:
        req = urllib2.Request(url, txdata, txheaders) # create a request object
        handle = opener.open(req) # and open it to return a handle on the url
        data = handle.read()
        f = file('posterior_samples.dat', 'w')
        f.write(data)
        f.close()

    except IOError, e:
        print 'We failed to open "%s".' % url
        if hasattr(e, 'code'):
            print 'We failed with error code - %s.' % e.code
        elif hasattr(e, 'reason'):
            print "The error object has the following 'reason' attribute :", e.reason
            print "This usually means the server doesn't exist, is down, or we don't have an internet connection."
            sys.exit()
    else:
        print 'Here are the headers of the page :'
        print handle.info() # handle.read() returns the page, handle.geturl() returns the true url of the page fetched (in case urlopen has followed any redirects, which it sometimes does)

    return HTMLSource

def all_pairs(L):
    while L:
        i = L.pop()
        for j in L: yield i, j

def open_url_wget(url,un=None,pw=None,args=[]):
    import subprocess
    import urlparse

    if un is not None and pw is not None:
        args+=["--user",un,"--password",pw,"--no-check-certificate"]
    retcode=subprocess.call(['wget']+[url]+args)

    return retcode

def test_and_switch_param(common_output_table_header,test,switch):
    try:
        idx=common_output_table_header.index(test)
        common_output_table_header[idx]=switch
        print "Re-labelled %s -> %s"%(test,switch)
    except:
        pass

    return

def compare_plots_one_param_pdf(list_of_pos_by_name,param):
    """
    Plots a gaussian kernel density estimate for a set
    of Posteriors onto the same axis.

    @param list_of_pos: a list of Posterior class instances.

    @param plot1DParams: a dict; {paramName:Nbins}

    """

    from scipy import seterr as sp_seterr

    #Create common figure
    myfig=plt.figure(figsize=(10,8),dpi=150)

    list_of_pos=list_of_pos_by_name.values()
    list_of_pos_names=list_of_pos_by_name.keys()

    min_pos=np.min(list_of_pos[0][param].samples)
    max_pos=np.max(list_of_pos[0][param].samples)

    gkdes={}
    injvals=[]
    for name,posterior in list_of_pos_by_name.items():

        pos_samps=posterior[param].samples
        if posterior[param].injval is not None:
            injvals.append(posterior[param].injval)

        min_pos_temp=np.min(pos_samps)
        max_pos_temp=np.max(pos_samps)

        if min_pos_temp<min_pos:
            min_pos=min_pos_temp
        if max_pos_temp>max_pos:
            max_pos=max_pos_temp

        injpar=posterior[param].injval

        gkdes[name]=posterior[param].gaussian_kde

    if gkdes:
        ind=np.linspace(min_pos,max_pos,101)

        kdepdfs=[]
        for name,gkde in gkdes.items():
            kdepdf=gkde.evaluate(ind)
            kdepdfs.append(kdepdf)
            plt.plot(ind,np.transpose(kdepdf),label=name)
        plt.grid()
        plt.legend()
        plt.xlabel(param)
        plt.ylabel('Probability Density')

        if injvals:
            print "Injection parameter is %f"%(float(injvals[0]))
            injpar=injvals[0]
            if min(pos_samps)<injpar and max(pos_samps)>injpar:
                plt.plot([injpar,injpar],[0,max(kdepdf)],'r-.',scalex=False,scaley=False)

    #

    return myfig#,rkde
#
def compare_plots_one_param_line_hist(list_of_pos_by_name,param,cl,color_by_name,cl_lines_flag=True):


    """
    Plots a gaussian kernel density estimate for a set
    of Posteriors onto the same axis.

    @param list_of_pos: a list of Posterior class instances.

    @param plot1DParams: a dict; {paramName:Nbins}

    """

    from scipy import seterr as sp_seterr

    #Create common figure
    myfig=plt.figure(figsize=(18,12),dpi=300)
    myfig.add_axes([0.1,0.1,0.65,0.85])
    list_of_pos=list_of_pos_by_name.values()
    list_of_pos_names=list_of_pos_by_name.keys()

    injvals=[]

    patch_list=[]
    for name,posterior in list_of_pos_by_name.items():
        colour=color_by_name[name]
        myfig.gca(autoscale_on=True)


        (n, bins, patches)=plt.hist(posterior[param].samples,bins=100,histtype='step',label=name,normed=True,hold=True,color=color_by_name[name])#range=(min_pos,max_pos)

        patch_list.append(patches[0])

    max_y=myfig.gca().get_ylim()[1]

    top_cl_intervals_list={}
    pos_names=list_of_pos_by_name.keys()


    for name,posterior in list_of_pos_by_name.items():
        #toppoints,injectionconfidence,reses,injection_area,cl_intervals=bppu.greedy_bin_one_param(posterior,{param:greedyBinSizes[param]},[cl])
        cl_intervals=posterior[param].prob_interval([cl])
        colour=color_by_name[name]
        if cl_intervals[0] is not None and cl_lines_flag:
            try:
                plt.plot([cl_intervals[0][0],cl_intervals[0][0]],[0,max_y],color=colour,linestyle='--')
                plt.plot([cl_intervals[0][1],cl_intervals[0][1]],[0,max_y],color=colour,linestyle='--')
            except:
                print "MAX_Y",max_y,[cl_intervals[0][0],cl_intervals[0][0]],[cl_intervals[0][1],cl_intervals[0][1]]
        top_cl_intervals_list[name]=(cl_intervals[0][0],cl_intervals[0][1])

    if cl_lines_flag:
        pos_names.append(str(int(cl*100))+'%')
        patch_list.append(mpl.lines.Line2D(np.array([0.,1.]),np.array([0.,1.]),linestyle='--',color='black'))

    plt.grid()

    oned_legend=plt.figlegend(patch_list,pos_names,'right')
    for text in oned_legend.get_texts():
        text.set_fontsize('small')
    plt.xlabel(param)
    plt.ylabel('Probability density')
    plt.draw()

    if injvals:
        print "Injection parameter is %f"%(float(injvals[0]))
        injpar=injvals[0]
        if min(pos_samps)<injpar and max(pos_samps)>injpar:
            plt.plot([injpar,injpar],[0,max(kdepdf)],'r-.',scalex=False,scaley=False)

    #

    return myfig,top_cl_intervals_list#,rkde


def compare_bayes(outdir,names_and_pos_folders,injection_path,eventnum,username,password,reload_flag,clf):

    injection=None

    if injection_path is not None and os.path.exists(injection_path) and eventnum is not None:
        eventnum=int(eventnum)
        import itertools
        injections = SimInspiralUtils.ReadSimInspiralFromFiles([injection_path])
        if eventnum is not None:
            if(len(injections)<eventnum):
                print "Error: You asked for event %d, but %s contains only %d injections" %(eventnum,injection_path,len(injections))
                sys.exit(1)
            else:
                injection=injections[eventnum]


    peparser=bppu.PEOutputParser('common')
    pos_list={}
    tp_list={}
    common_params=None
    working_folder=os.getcwd()
    for name,pos_folder in names_and_pos_folders:
        import urlparse

        pos_folder_url=urlparse.urlparse(pos_folder)

        if 'http' in pos_folder_url.scheme:

            """
            Retrieve a file over http(s).
            """
            downloads_folder=os.path.join(os.getcwd(),"downloads")
            pos_folder_parse=urlparse.urlsplit(pos_folder)
            head,tail=os.path.split(pos_folder_parse.path)
            if tail is 'posplots.html' or tail:
                pos_file_part=head
            else:
                pos_file_part=pos_folder_parse.path
            pos_file_url=urlparse.urlunsplit((pos_folder_parse.scheme,pos_folder_parse.netloc,os.path.join(pos_file_part,'posterior_samples.dat'),'',''))
            print pos_file_url
            pos_file=os.path.join(os.getcwd(),downloads_folder,"%s.dat"%name)

            if not os.path.exists(pos_file):
                reload_flag=True

            if reload_flag:
                if os.path.exists(pos_file):
                    os.remove(pos_file)
                if not os.path.exists(downloads_folder):
                    os.makedirs(downloads_folder)
                open_url_wget(pos_file_url,un=username,pw=password,args=["-O","%s"%pos_file])


        elif pos_folder_url.scheme is '' or pos_folder_url.scheme is 'file':
            pos_file=os.path.join(pos_folder,'posterior_samples.dat')

        else:
            print "Unknown scheme for input data url: %s\nFull URL: %s"%(pos_folder_url.scheme,str(pos_folder_url))
            exit(0)

        print "Reading posterior samples from %s ..."%pos_file

        common_output_table_header,common_output_table_raw=peparser.parse(open(pos_file,'r'))

        test_and_switch_param(common_output_table_header,'distance','dist')
        test_and_switch_param(common_output_table_header,'chirpmass','mchirp')
        test_and_switch_param(common_output_table_header,'mc','mchirp')

        if 'LI_MCMC' in name or 'FU_MCMC' in name:

            try:

                idx=common_output_table_header.index('iota')
                print "Inverting iota!"

                common_output_table_raw[:,idx]= np.pi*np.ones(len(common_output_table_raw[:,0])) - common_output_table_raw[:,idx]

            except:
                pass


        try:
            print "Converting iota-> cos(iota)"
            idx=common_output_table_header.index('iota')
            common_output_table_header[idx]='cos(iota)'
            common_output_table_raw[:,idx]=np.cos(common_output_table_raw[:,idx])
        except:
            pass

        pos_temp=bppu.Posterior((common_output_table_header,common_output_table_raw),SimInspiralTableEntry=injection)

        try:
            idx=common_output_table_header.index('m1')

            idx2=common_output_table_header.index('m2')

            if pos_temp['m1'].mean<pos_temp['m2'].mean:
                print "SWAPPING MASS PARAMS!"
                common_output_table_header[idx]='x'
                common_output_table_header[idx2]='m1'
                common_output_table_header[idx]='m2'
                pos_temp=bppu.Posterior((common_output_table_header,common_output_table_raw),SimInspiralTableEntry=injection)
        except:
            pass

        pos_list[name]=pos_temp

        if common_params is None:
            common_params=pos_temp.names
        else:
            set_of_pars = set(pos_temp.names)
            common_params=list(set_of_pars.intersection(common_params))

    print "Common parameters are %s"%str(common_params)

    set_of_pars = set(allowed_params)
    common_params=list(set_of_pars.intersection(common_params))

    print "Using parameters %s"%str(common_params)

    if not os.path.exists(os.path.join(os.getcwd(),'results')):
        os.makedirs('results')

    if not os.path.exists(outdir):
        os.makedirs(outdir)

    greedy2savepaths=[]
    my_color_converter=mpl_colors.ColorConverter()
    if common_params is not [] and common_params is not None: #If there are common parameters....
        colorlst=bppu.__default_color_lst

        if len(common_params)>1: #If there is more than one parameter...
            temp=copy.copy(common_params)
            #Plot two param contour plots

            #Assign some colours to each different analysis
            color_by_name={}
            for name,infolder in names_and_pos_folders:
                color_by_name[name]=my_color_converter.to_rgb((random.uniform(0.1,1),random.uniform(0.1,1),random.uniform(0.1,1)))

            for i,j in all_pairs(temp):#Iterate over all unique pairs in the set of common parameters
                pplst=[i,j]
                rpplst=pplst[:]
                rpplst.reverse()

                pplst_cond=(pplst in twoDplots)
                rpplst_cond=(rpplst in twoDplots)
                if pplst_cond or rpplst_cond:#If this pair of parameters is in the plotting list...

                    try:
                        greedy2Params={i:greedyBinSizes[i],j:greedyBinSizes[j]}
                    except KeyError:
                        continue

                    name_list=[]
                    cs_list=[]

                    cllst=[0.95]
                    slinestyles=['solid', 'dashed', 'dashdot', 'dotted']

                    fig=bppu.plot_two_param_greedy_bins_contour(pos_list,greedy2Params,cllst,color_by_name)

                    greedy2savepaths.append('%s-%s.png'%(pplst[0],pplst[1]))
                    fig.savefig(os.path.join(outdir,'%s-%s.png'%(pplst[0],pplst[1])))


            plt.clf()
        oned_data={}
        for param in common_params:
            print "Plotting comparison for '%s'"%param


            cl_table_header='<table><th>Run</th>'
            cl_table={}
            save_paths=[]
            cl_table_min_max_str='<tr><td> Min | Max </td>'
            for confidence_level in confidenceLevels:

                cl_table_header+='<th colspan="2">%i%% (Lower|Upper)</th>'%(int(100*confidence_level))
                hist_fig,cl_intervals=compare_plots_one_param_line_hist(pos_list,param,confidence_level,color_by_name,cl_lines_flag=clf)

                save_path=''
                if hist_fig is not None:
                    save_path=os.path.join(outdir,'%s_%i.png'%(param,int(100*confidence_level)))
                    hist_fig.savefig(save_path)
                    save_paths.append(save_path)
                min_low,max_high=cl_intervals.values()[0]
                for name,interval in cl_intervals.items():
                    low,high=interval
                    if low<min_low:
                        min_low=low
                    if high>max_high:
                        max_high=high
                    try:
                        cl_table[name]+='<td>%s</td><td>%s</td>'%(low,high)
                    except:
                        cl_table[name]='<td>%s</td><td>%s</td>'%(low,high)
                cl_table_min_max_str+='<td>%s</td><td>%s</td>'%(min_low,max_high)
            cl_table_str=cl_table_header
            for name,row_contents in cl_table.items():
                cl_table_str+='<tr><td>%s<font color="%s"></font></td>'%(name,str(mpl_colors.rgb2hex(color_by_name[name])))#,'&#183;'.encode('utf-8'))

                cl_table_str+=row_contents+'</tr>'
            cl_table_str+=cl_table_min_max_str+'</tr>'
            cl_table_str+='</table>'
            oned_data[param]=(save_paths,cl_table_str)



    return greedy2savepaths,oned_data

if __name__ == '__main__':
    from optparse import OptionParser
    parser=OptionParser()
    parser.add_option("-o","--outpath", dest="outpath",help="Make page and plots in DIR.", metavar="DIR")
    parser.add_option("-p","--pos",dest="pos_list",action="append",help="Path to folders containing output of cbcBayesPostProc.")
    parser.add_option("-n","--name",dest="names",action="append",help="Name of posterior result e.g. followupMCMC 2.5PN (optional)")
    parser.add_option("-i","--inj",dest="inj",help="Path of injection XML containing SimInspiralTable (optional).")
    parser.add_option("-e","--eventnum",dest="eventnum",help="Sim ID of injection described in injection XML (optional).")
    parser.add_option("-u",dest="username",help="User name for https authenticated content (optional).")
    parser.add_option("-x",dest="password",help="Password for https authenticated content (optional).")
    parser.add_option("--reload",dest="reload_flag",action="store_true",help="Re-download all pos files (optional).")
    parser.add_option("--hide-cl-lines",dest="clf",action="store_false",default=True,help="Hide confidence level lines on 1D plots for clarity (optional).")
    (opts,args)=parser.parse_args()

    if opts.outpath is None:
        print "No output directory specified. Output will be saved to PWD : %s"%os.getcwd()
        outpath=os.getcwd()
    else:
        outpath=opts.outpath

    if opts.pos_list is None:
        print "No input paths given!"
        exit(1)

    if opts.names is None:
        print "No names given, making some up!"
        names=[]
        for i in range(len(opts.pos_list)):
            names.append(str(i))
    else:
        names=opts.names

    if len(opts.pos_list)!=len(names):
        print "Either add names for all posteriors or dont put any at all!"

    greedy2savepaths,oned_data=compare_bayes(outpath,zip(names,opts.pos_list),opts.inj,opts.eventnum,opts.username,opts.password,opts.reload_flag,opts.clf)

    ####Print HTML!#######

    compare_page=bppu.htmlPage('Compare PDFs (single event)',css=bppu.__default_css_string)

    param_section=compare_page.add_section('Meta')

    param_section_write='<div><p>This comparison was created from the following analyses</p><ul>'
    for name,input_file in zip(names,opts.pos_list):
        param_section_write+='<li><a href="%s">%s</a></li>'%(input_file,name)
    param_section_write+='</ul></div>'

    param_section.write(param_section_write)
    if oned_data:

        param_section=compare_page.add_section('1D marginal posteriors')

        for param_name,data in oned_data.items():
            param_section.h3(param_name)
            save_paths,cl_table_str=data
            clf_toggle=False
            for save_path in save_paths:
                head,plotfile=os.path.split(save_path)
                if not clf_toggle:
                    clf_toggle=(not opts.clf)
                    param_section.write('<img src="%s"/>'%str(plotfile))

            param_section.write(cl_table_str)

    if greedy2savepaths:

        param_section=compare_page.add_section('2D greedy bin historgrams')
        for plot_path in greedy2savepaths:
            temp,param_name=os.path.split(plot_path)
            param_name=param_name.split('.')[0]
            head,plotfile=os.path.split(plot_path)
            param_section.write('<img src="%s"/>'%str(plotfile))#str(os.path.relpath(plot_path,outpath)))



    compare_page_footer=compare_page.add_section('')
    compare_page_footer.p('Produced using cbcBayesCompPos.py at '+strftime("%Y-%m-%d %H:%M:%S")+' .')

    cc_args=copy.deepcopy(sys.argv)

    #remove username and password
    try:
        user_idx=cc_args.index('-u')
        cc_args[user_idx+1]='<LIGO username>'
    except:
        pass

    try:
        pass_idx=cc_args.index('-x')
        cc_args[pass_idx+1]='<LIGO password>'
    except:
        pass

    cc_args_str=''
    for cc_arg in cc_args:
        cc_args_str+=cc_arg+' '

    compare_page_footer.p('Command line: %s'%cc_args_str)
    compare_page_footer.p(git_version.verbose_msg)


    resultspage=open(os.path.join(outpath,'index.html'),'w')
    resultspage.write(str(compare_page))
    resultspage.close()
